# DynarmicTest14 network reference

DynarmicTest14 was inspected as the last known working network reference. Its worker model runs the guest networking routine synchronously and includes blocking compatibility behavior. Copying that model wholesale into the v22 beta runtime would put long DNS/connect/send/receive waits back on the render/UI thread.

NetworkTest3 therefore uses DynarmicTest14 only to confirm guest ABI, socket import, and cooperative-worker control-flow expectations. NetworkTest2 remains the code baseline. The only implementation change is hard wall-clock preemption around the existing frame-sliced worker.
