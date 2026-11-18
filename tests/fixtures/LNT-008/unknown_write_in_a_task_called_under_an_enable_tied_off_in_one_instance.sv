// sverity-args: --top top
// The only call of t sits under an enable u0 leaves free and u1 ties to 0: q of u1 is never driven, which the shared body cannot say.
// expect-unresolved: LNT-008 5
module child(input logic clk, en, input logic [3:0] a, output logic [3:0] y);
  logic [3:0] q;
  task automatic t(input logic [3:0] v); q <= v; endtask
  always_ff @(posedge clk) if (en) t(a);
  assign y = q;
endmodule
module top(input logic clk, en, input logic [3:0] a, output logic [3:0] y0, y1);
  child u0(.clk, .en, .a, .y(y0));
  child u1(.clk, .en(1'b0), .a, .y(y1));
endmodule
