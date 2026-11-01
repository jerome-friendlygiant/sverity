// sverity-args: --top top
// expect-none: STY-005
module child #(parameter W = 1) (input logic [W-1:0] a, output logic [W-1:0] y);
  assign y = a;
endmodule
module top(input logic [3:0] a, output logic [3:0] y);
  child #(.W(4)) u0 (.a(a), .y(y));
endmodule
