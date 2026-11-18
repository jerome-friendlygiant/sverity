// sverity-args: --top top
// One diagnostic location, two specialisations: the case is complete for W = 1 and leaves 2 and 3 uncovered for W = 2, so the missing
// default is a defect there and the shared finding stays a warning.
// expect: LNT-065 WARNING 9
// expect-none: LNT-065 INFO
module sub #(parameter int W = 1)(input logic [W-1:0] s, input logic [3:0] d, output logic y);
  always_comb begin
    y = 1'b0;
    case (s)
      1'd0: y = d[0];
      1'd1: y = d[1];
    endcase
  end
endmodule
module top(input logic [1:0] s2, input logic s1, input logic [3:0] d, output logic y1, y2);
  sub #(.W(1)) u1(.s(s1), .d(d), .y(y1));
  sub #(.W(2)) u2(.s(s2), .d(d), .y(y2));
endmodule
