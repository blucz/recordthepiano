//
// Copyright (C) 2010 Jackson Harper (jackson@manosdemono.com)
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
// 
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//

using System;
using System.Linq;
using System.Collections;
using System.Collections.Generic;

namespace Http
{
    /// <summary>
    /// A Hierarchical dictionary. Data can live at the "root" level, or in
    /// child dictionaries. DataDictionaries can store string, lists of string
    /// and dictionarys with strings as their key and string as their values.
    ///
    /// To add a list to the HttpDataDictionary simply add an item with a [] at
    /// the end of the keyname. foo[]=1&foo[]=2
    ///
    /// To add a dictionary you just add an item in this format keyname[key]=.
    /// foo[a]=1&foo[b]=2
    /// </summary>
    public class HttpDataDictionary
    {
        private Dictionary<string,object> dictionary;
        private List<HttpDataDictionary> children;

        public HttpDataDictionary ()
        {
            dictionary = new Dictionary<string, object> ();
        }

        /// <summary>
        /// Get or set the string value at the specified key.
        /// </summary>
        /// <param name="key">
        /// A <see cref="System.String"/>
        /// </param>
        public string this [string key] {
            get { return Get(key); }
            set { Set (key, value); }
        }

        public ICollection<string> Keys {
            get { return dictionary.Keys; }
        }

        public IEnumerable<string> AllKeys {
            get {
                foreach (string k in dictionary.Keys)
                    yield return k;
                if (children != null) {
                    foreach (var child in children)
                        foreach (string k in child.AllKeys)
                        yield return k;
                }
            }
        }

        /// <summary>
        /// The sum of child dictionaries count and the count
        /// of the keys in this dictionary.
        /// </summary>
        public int Count {
            get {
                int sum = 0;
                if (children != null)
                    children.Sum (c => c.Count);
                return sum + dictionary.Count;
            }
        }

        /// <summary>
        /// The child dictionaries.
        /// </summary>
        public IList<HttpDataDictionary> Children {
            get {
                if (children == null)
                    children = new List<HttpDataDictionary>();
                return children;
            }
        }

        /// <summary>
        /// A list of strings stored with the specified key.  Lists are created
        /// when an item is added to the dictionary with [] at the end of its
        /// name.
        /// </summary>  
        public IList<string> GetList (string key)
        {
            return Get<IList<string>>(key);
        }

        /// <summary>
        /// A dictionary of strings.  Dictionaries are created when an item is
        /// added to the HttpDataDictionary with a key in the format: foo[key].
        /// </summary>
        public IDictionary<string,string> GetDict (string key)
        {
            return Get<IDictionary<string,string>> (key);
        }

        public string Get(string key) {
            return Get<string>(key);
        }

        private T Get<T> (string key)
        {
            object value = null;
            T t = default (T);

            if (dictionary.TryGetValue (key, out value)) {
                if (value is T)
                    return (T) value;
            }

            if (children != null)
                children.Where(d => (t = d.Get<T> (key)) != null).FirstOrDefault ();

            return t;
        }

        /// <summary>
        /// Remove all elements from this dictionary, and remove all references to child dictionaries.
        /// </summary>
        public void Clear ()
        {
            dictionary.Clear ();
            children = null;
        }	

        /// <summary>
        /// Assign a value into this dictionary with the specified key.
        /// </summary>
        public void Set (string key, string value)
        {
            int open = key.IndexOf ('[');
            if (open == -1) {
                dictionary [key] = value;
                return;
            }

            string elkey = key.Substring (0, open);
            int close = key.IndexOf (']');
            if (close == -1 || close < open) {
                dictionary [elkey] = value;
                return;
            }

            object col;
            if (close == open + 1) {
                List<string> list = null;

                if (dictionary.TryGetValue (elkey, out col)) {
                    list = col as List<string>;
                    if (list != null) {
                        list.Add (value);
                        return;
                    }
                }

                list = new List<string> ();
                list.Add (value);
                dictionary [elkey] = list;

                return;
            }

            Dictionary<string,string> dict = null;
            string dname = key.Substring (open + 1, close - open - 1);
            if (dictionary.TryGetValue (elkey, out col)) {
                dict = col as Dictionary<string,string>;
                if (dict != null) {
                    dict [dname] = value;
                    return;
                }
            }

            dict = new Dictionary<string,string> ();
            dict [dname] = value;
            dictionary [elkey] = dict;
        }
    }
}

