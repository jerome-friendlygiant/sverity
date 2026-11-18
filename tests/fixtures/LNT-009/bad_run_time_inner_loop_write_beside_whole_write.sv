// sverity-args: --top top
// The inner loop's bound is a run-time value, but its write exists in the hardware and runs in outer iteration 0
// whenever n > 0: two procedures write all of g.
// expect: LNT-009 ERROR 11
module top(input logic c, input logic [3:0] n, output logic y);
  logic g;
  always @(c, n) for (int i = 0; i < 2; i++) begin
    for (int j = 0; j < n; j++) g = c;
    if (i == 1) break;
  end
  always @(c) g = 1'b0;
  assign y = g;
endmodule
