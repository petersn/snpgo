#!/usr/bin/python

import BaseHTTPServer

class Handler(BaseHTTPServer.BaseHTTPRequestHandler):
	def do_GET(self):
		self.send_response(200)
		self.end_headers()
		self.wfile.write("Hello, world.")

if __name__ == "__main__":
	BaseHTTPServer.HTTPServer(("", 13697), Handler).serve_forever()

