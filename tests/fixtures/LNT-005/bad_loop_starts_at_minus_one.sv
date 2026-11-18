// sverity-args: --top top
// The loop variable starts at -1, so the first value is negative and the signed compare with the unsigned bound is not value-preserving.
// revert: for (int i = -1; => for (int i = 0;
// revert-expect-none: LNT-005 WARNING
// expect: LNT-005 WARNING 10
module top(input logic [7:0] d, output logic [3:0] n);
  localparam int unsigned LIMIT = 8;
  always_comb begin
    n = '0;
    for (int i = -1; i < LIMIT; i++) if (i >= 0) n += d[i];
  end
endmodule
