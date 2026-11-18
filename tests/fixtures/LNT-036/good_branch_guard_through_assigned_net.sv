// sverity-args: --top top
// The lane index `(N - 1 - ctr_q) * 128` wraps at ctr_q = N, which the branch excludes: directly (`ctr_q < N`) and through a net
// assigned that comparison (`done`), the guard of the original wrap.
// expect-none: LNT-036
module top(input logic enable, inc, input logic [127:0] d, input logic [1:0] ctr_q,
           output logic [383:0] a, b);
  localparam int unsigned N = 3;
  logic done;
  assign done = (ctr_q >= N);
  always_comb begin
    a = '0;
    if (ctr_q < N) a[(N - 1 - ctr_q) * 128 +: 128] = d;
  end
  always_comb begin
    b = '0;
    if (!enable || done) b = '0;
    else if (inc) b[(N - 1 - ctr_q) * 128 +: 128] = d;
  end
endmodule
