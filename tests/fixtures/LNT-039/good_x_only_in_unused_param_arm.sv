// sverity-args: --top top
// The X arm of a parameter value's ?: is not selected and the parameter is never used: a parameter declaration or override
// is not an assigned value, so nothing is reported.
// expect-none: LNT-039
module sub #(parameter logic [1:0] P = 2'b00) (output logic [1:0] o);
  assign o = 2'b00;
endmodule
module top(output logic [1:0] y);
  localparam bit C = 1'b1;
  localparam logic [1:0] Q = C ? 2'b01 : 2'bx1;
  sub #(.P(C ? 2'b01 : 2'bx1)) u(.o(y));
endmodule
