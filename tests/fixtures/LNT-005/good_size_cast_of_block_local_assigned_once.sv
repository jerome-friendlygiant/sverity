// sverity-args: --top top
// `idx` is written once, before the cast, and is a remainder below 6: the 3-bit cast holds it unchanged.
// expect-none: LNT-005 WARNING
module top(input logic [2:0] ptr, output logic [2:0] y [6]);
  localparam int N = 6;
  always_comb begin
    for (int k = 0; k < N; k++) begin
      int idx;
      idx = (int'(ptr) + k) % N;
      y[k] = 3'(idx);
    end
  end
endmodule
