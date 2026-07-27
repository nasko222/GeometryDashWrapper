# NetworkTest6 — safe asynchronous HTTP worker

This branch is based on NetworkTest4's responsive asynchronous network model,
not NetworkTest5's synchronous run-to-wait experiment.

## Runtime change

Only `src/dynarmic_probe.cpp` changes relative to NetworkTest5's runtime source.
The HTTP worker is started by the next frame pump after a signal, and synthetic
SVC return stubs are completed atomically before worker state can be suspended.

## Expected diagnostics

A request should log:

- `wake=deferred-next-frame`
- `worker slice #... trigger=frame-pump`
- `completed pending stub return reason=after-worker-svc`
- slice yield lines whose saved PC is a real guest address, never `0x210xxxxx`

If the safety invariant fails, the wrapper dumps the import ring and exits with
`refused to save worker inside synthetic stub` instead of silently freezing.
