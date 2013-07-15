using System;
using System.Linq;
using System.IO;
using System.Text;
using System.Threading;
using System.Collections;
using System.Web;
using System.Collections.Generic;

using Http;
using ServiceStack.Redis;
using Amazon.S3;
using Amazon.S3.Model;

namespace Server
{
    class Recording {
        public long             RecordingId      { get; set; }
        public string           Key              { get; set; }
        public DateTime         When             { get; set; }
        public string           Title            { get; set; }
        public string           Notes            { get; set; }
        public int              Length           { get; set; }

        public Recording() { }

        public Recording(Dictionary<string,string> rec) {
            RecordingId = long.Parse(rec["recording_id"]);
            Key         = rec["key"];
            When        = DateTime.Parse(rec["when"]);
            Title       = rec["title"];
            Notes       = rec["notes"];
            Length      = int.Parse(rec["length"]);
        }

        public Hashtable ToJValue() {
            var ret = new Hashtable();
            ret["recording_id"] = RecordingId.ToString();
            ret["key"]          = Key;
            ret["when"]         = When.ToString();
            ret["title"]        = Title;
            ret["notes"]        = Notes;
            ret["length"]       = Length;
            ret["url"]          = "http://recordthepiano.s3.amazonaws.com/" + HttpUtility.UrlEncode(Key);
            return ret;
        }

        public Dictionary<string,string> ToDictionary() {
            var ret = new Dictionary<string,string>();
            ret["recording_id"] = RecordingId.ToString();
            ret["key"]          = Key;
            ret["when"]         = When.ToString();
            ret["title"]        = Title;
            ret["notes"]        = Notes;
            ret["length"]       = Length.ToString();
            return ret;
        }

        public override string ToString() {
            return string.Format("Recording[Id={0}, Key={1}, Length={2}, Title={3}]", RecordingId, Key, Length, Title);
        }
    }
    
    class MainClass {
        static void _Usage() { 
            Console.Error.WriteLine("server -webroot=/path/to/webroot -data=/path/to/webroot");
            Environment.Exit(1);
        }

        const string AwsAccessKey = "AKIAIXHWOIWWYZN3IFKQ";
        const string AwsSecretKey = "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";

        static string                         _datadir;
        static string                         _webroot;
        static Timer                          _rescantimer;
        static AmazonS3Client                 _s3               = new AmazonS3Client(AwsAccessKey, AwsSecretKey);

        public static void Main (string[] args) {
            _datadir = "data";
            _webroot = "webroot";

            foreach (var arg in args) {
                if (arg.StartsWith("-webroot=")) {
                    _webroot = arg.Substring("-webroot=".Length).TrimEnd('/');
                } else if (arg.StartsWith("-data=")) {
                    _datadir = arg.Substring("-data=".Length).TrimEnd('/');
                } else if (arg.Contains("--help")) {
                    _Usage();
                } else {
                    Console.Error.WriteLine("unknown argument: " + arg);
                    Environment.Exit(1);
                }
            }

            if (!Directory.Exists(_datadir)) {
                Directory.CreateDirectory(_datadir);
            }

            if (!Directory.Exists(_webroot)) {
                Console.Error.WriteLine("webroot directory not found: " + _webroot);
                Environment.Exit(1);
            }

            System.Net.ServicePointManager.ServerCertificateValidationCallback += (sender,cert,chain,errors) => true;

            Http.HttpServer server = new Http.HttpServer(8002);
            server.HandleRequest += ev_request;
            server.Start();
            Console.Error.WriteLine("Listening on port {0}", server.BoundPort);

            _rescantimer = new Timer(ev_periodic_rescan, null, 0, (int)TimeSpan.FromMinutes(5).TotalMilliseconds);

            new ManualResetEvent(false).WaitOne();
        }

        static void ev_periodic_rescan(object state) {
            try {
                Rescan();
            } catch (Exception e) {
                Console.Error.WriteLine("Error in rescan: " + e);
            }
        }

        static void Rescan() {
            Console.Error.WriteLine("Scanning S3 for new objects");
            var req = new ListObjectsRequest() {
                BucketName = "recordthepiano",
                MaxKeys    = 100,
            };
            var objects = new List<S3Object>();
            while (true) {
                var response = _s3.ListObjects(req);
                foreach (var obj in response.S3Objects) {
                    objects.Add(obj);
                }
                if (response.IsTruncated) {
                    req.Marker = response.NextMarker;
                    continue;
                }
                break;
            }

            int newobjects = 0;

            using (var redis = GetRedis()) {
                foreach (var obj in objects) {
                    var id  = redis.GetValueFromHash("recording:s3key_to_id", obj.Key);
                    if (string.IsNullOrEmpty(id)) {
                        var key    = obj.Key.Trim().Replace(".flac","");
                        var splits = key.Split(',').Select(x => x.Trim()).ToArray();
                        Console.WriteLine("'{0}' '{1}'", splits[0], splits[1]);
                        var date   = DateTime.Parse(splits[0]);
                        var length = int.Parse(splits[1].TrimEnd('s'));
                        var rec = new Recording() {
                            RecordingId      = redis.IncrementValue("recording:nextid"),
                            Key              = obj.Key,
                            When             = date,
                            Title            = "Recording from " + date.ToString(),
                            Notes            = "",
                            Length           = length,
                        };
                        Console.Error.WriteLine("Created recording: " + rec);
                        using (var tx = redis.CreateTransaction()) {
                            tx.QueueCommand(r => r.SetRangeInHash("recording:"+rec.RecordingId, rec.ToDictionary()));
                            tx.QueueCommand(r => r.AddItemToSet("recordings", rec.RecordingId.ToString()));
                            tx.QueueCommand(r => r.SetEntryInHash("recording:s3key_to_id", obj.Key, rec.RecordingId.ToString()));
                            tx.Commit();
                        }
                        ++newobjects;
                    }
                }
            }

            Console.Error.WriteLine("Finished rescan. {0} objects scanned, {1} created", objects.Count, newobjects);
        }

        static RedisClient GetRedis() { return new RedisClient("localhost", 6379); }

        static void ev_api_request(string method, HttpTransaction tx) {
            switch (method) {
                case "list": ev_list(tx);                                  break;
                case "scan": ev_scan(tx);                                  break;
                default:     tx.Response.Respond(HttpStatusCode.NotFound); break;
            }
        }

        static void ev_edit(HttpTransaction tx) {
            using (var redis = new RedisClient("localhost", 6379)) {
            }
        }

        static void ev_list(HttpTransaction tx) {
            var recordings = new List<Recording>();
            using (var redis = GetRedis()) {
                var keys = redis.GetAllItemsFromSet("recordings");
                foreach (var key in keys) {
                    var entries = redis.GetAllEntriesFromHash("recording:"+key);
                    recordings.Add(new Recording(entries));
                }
            }

            var response = new Hashtable();
            var list     = new ArrayList();
            foreach (var recording in recordings.OrderByDescending(x => x.When))
                list.Add(recording.ToJValue());
            response["recordings"] = list;

            tx.Response.Headers.ContentType = "text/json";
            tx.Response.Respond(HttpStatusCode.OK, Utils.JSON.JsonEncode(response));
        }

        static void ev_scan(HttpTransaction tx) {
            Rescan();
            tx.Response.Headers.ContentType = "text/json";
            tx.Response.Respond(HttpStatusCode.OK, "{ }");
        }

        static void ev_request(HttpTransaction tx) {
            try {
                var path = tx.Request.Path.TrimStart ('/').TrimEnd ('/');

                Console.Error.WriteLine("GOT REQUEST {0}", path);

                if (path == "") path = "index.html";

                if (path.StartsWith("api/")) {
                    ev_api_request(path.Substring("api/".Length), tx);
                } else if (path.Contains("..")) {
                    tx.Response.Respond(HttpStatusCode.NotFound);
                } else {
                    var fi = new FileInfo(Path.Combine(_webroot, path.Replace('/', Path.DirectorySeparatorChar)));
                    if (fi == null || fi.Exists) {
                        if (fi != null) {
                            try {
                                string if_modified_since;
                                if (tx.Request.Headers.TryGetValue("If-Modified-Since", out if_modified_since)) {
                                    if (fi.LastWriteTimeUtc <= DateTime.Parse(if_modified_since)) {
                                        tx.Response.Respond(HttpStatusCode.NotModified);
                                        return;
                                    }
                                }
                            } catch (Exception e) {
                                Console.Error.WriteLine("Error handling If-Modified-Since: " + e);
                            }
                        }
                        try {
                            tx.Response.Headers.SetHeader("Accept-Ranges", "bytes");
                            if (fi != null)
                                tx.Response.Headers.SetHeader("Last-Modified", fi.LastWriteTimeUtc.ToString());
                            var datastream = fi.Open(FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                            tx.Response.Headers.SetNormalizedHeader("Content-Type", MimeTypes.GetMimeType(fi.Name));

                            if (tx.Request.Method == HttpMethod.Head) {
                                tx.Response.Respond(HttpStatusCode.OK);

                            } else if (tx.Request.Method == HttpMethod.Get) {
                                var range = tx.Request.Headers.Range;
                                if (range != null) {
                                    datastream.Position = range.Offset;

                                    long real_count = datastream.Length - datastream.Position;
                                    if (range.Count.HasValue)
                                        real_count = Math.Min(real_count, range.Count.Value);
                                    tx.Response.Headers.ContentRange = new HttpContentRange(range.Offset, 
                                                                                           real_count,
                                                                                           datastream.Length);
                                    tx.Response.Headers.ContentLength = real_count;
                                    if (range.Count.HasValue) {
                                        tx.Response.Respond(HttpStatusCode.PartialContent, datastream, range.Count.Value);
                                    } else {
                                        tx.Response.Respond(HttpStatusCode.PartialContent, datastream);
                                    }
                                } else {
                                    tx.Response.Respond(HttpStatusCode.OK, datastream);
                                }
                            }
                        } catch {
                            tx.Response.Respond(HttpStatusCode.InternalServerError);
                        }
                    } else {
                        tx.Response.Respond(HttpStatusCode.NotFound);
                    }
                }
            } catch (Exception e) {
                Console.Error.WriteLine("Error in request handler: " + e);
            }
        }
    }
}
