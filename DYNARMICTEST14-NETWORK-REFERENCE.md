# DynarmicTest14 network reference

DynarmicTest14 remains the behavioral proof that the APK/GDPS request data and
server endpoints can work through this wrapper family. NetworkTest2 through
NetworkTest6 attempted to reproduce its guest pthread/libcurl execution model,
but the selected 2.2 beta repeatedly stopped during guest OpenSSL initialization
before reaching DNS or a socket.

NetworkTest7 therefore does **not** copy DynarmicTest14's worker scheduler. It
uses DynarmicTest14 only as the known-working functional reference and replaces
the transport boundary completely:

1. hook `CCHttpClient::send(CCHttpRequest*)` before guest worker creation;
2. copy the guest URL, method, body, and headers;
3. perform HTTP/HTTPS with WinHTTP on a real Windows host thread;
4. construct the beta's real `CCHttpResponse` ABI in guest memory;
5. invoke the original member callback on the main frame thread;
6. release the response through the guest `CCObject` lifecycle.

No guest CCHttpClient pthread, libcurl, OpenSSL, DNS, or socket code is executed
for hooked requests. All unrelated v22 editor, save, audio, input, APK-cache,
lifecycle, and companion-library fixes remain inherited from NetworkTest6.
