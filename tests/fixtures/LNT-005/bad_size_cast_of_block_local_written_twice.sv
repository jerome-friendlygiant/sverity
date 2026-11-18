// sverity-args: --top top
// Lookalike of the block local written once: a second write from an input is the value the cast reads for k == 3.
// expect: LNT-005 WARNING 11
module top(input logic [2:0] ptr, input int shift, output logic [2:0] y [6]);
  localparam int N = 6;
  always_comb begin
    for (int k = 0; k < N; k++) begin
      int idx;
      idx = (int'(ptr) + k) % N;
      if (k == 3) idx = shift;
      y[k] = 3'(idx);
    end
  end
endmodule
