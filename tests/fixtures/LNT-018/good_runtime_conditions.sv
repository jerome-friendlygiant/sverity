// sverity-args: --top top
// runtime if, ?: and case conditions have no constant value: no finding
// expect-none: LNT-018
module top(input logic a, b, input logic [1:0] s, output logic y, z, w);
  always_comb begin
    y = a;
    if (a) y = b;
    else y = ~b;
  end
  assign z = s[0] ? a : b;
  always_comb begin
    w = a;
    case (s)
      2'd0: w = a;
      2'd1: w = b;
      default: w = 1'b0;
    endcase
  end
endmodule
