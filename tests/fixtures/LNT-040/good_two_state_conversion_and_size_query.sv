// sverity-args: --top top
// X/Z bits do not pass through a conversion to a 2-state type or a size query; $unsigned and a 4-state cast keep them.
// expect-none: LNT-040
module top(input logic [3:0] a, output logic y1, y2, y3);
  localparam logic [3:0] P = 4'bx001;
  assign y1 = (a == 4'(int'(P)));
  assign y2 = ($bits(P) == 4) && ($countones(P) == 1);
  assign y3 = ($clog2(P) == 2);
endmodule
