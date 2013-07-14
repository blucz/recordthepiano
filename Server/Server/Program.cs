using System;
using System.IO;
using System.Text;
using System.Threading;
using System.Collections.Generic;

using Http;

namespace Server
{
    class MainClass {
        static void _Usage() { 
            Console.Error.WriteLine("server -webroot=/path/to/webroot -data=/path/to/webroot");
            Environment.Exit(1);
        }

        static string _datadir;
        static string _webroot;

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

            Http.HttpServer server = new Http.HttpServer(9999);
            server.HandleRequest += ev_request;
            server.Start();
            Console.Error.WriteLine("Listening on port {0}", server.BoundPort);
            new ManualResetEvent(false).WaitOne();
        }

        static void ev_api_request(string method, HttpTransaction tx) {
            switch (method) {
                case "list": ev_list(tx);                                  break;
                case "scan": ev_scan(tx);                                  break;
                default:     tx.Response.Respond(HttpStatusCode.NotFound); break;
            }
        }

        static void ev_edit(HttpTransaction tx) {
        }

        static void ev_list(HttpTransaction tx) {
        }

        static void ev_scan(HttpTransaction tx) {
        }

        static void ev_request(HttpTransaction tx) {
            var path = tx.Request.Path.TrimStart ('/').TrimEnd ('/');

            Console.Error.WriteLine("GOT REQUEST {0}", path);

            if (path == "") path = "index.html";)

            if (path.StartsWith("api/")) {
                ev_api_request(path.Substring("api/".Length), tx);
                tx.Response.Respond(HttpStatusCode.NotFound);
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
        }
    }
}
