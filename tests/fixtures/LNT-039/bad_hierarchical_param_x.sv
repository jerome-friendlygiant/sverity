// sverity-args: --top top
// A named constant reached through a hierarchical reference has the same value: its X bits reach the assigned value.
// expect: LNT-039 WARNING 10
module sub(output logic o);
  localparam logic [1:0] PX = 2'bx1;
  assign o = 1'b0;
endmodule
module top(output logic [1:0] y, output logic o);
  sub u(.o(o));
  assign y = u.PX;
endmodule
