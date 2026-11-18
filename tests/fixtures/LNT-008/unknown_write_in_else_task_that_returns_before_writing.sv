// sverity-args: --top top
// u0 ties the enable to 0 and u1 to 1: the else task returns before its write, so x of u0 is never driven, which the shared body cannot say.
// expect-unresolved: LNT-008 5
module child(input logic en, input logic [7:0] d, output logic [7:0] y);
  logic [7:0] x;
  task automatic t(); return; x = ~d; endtask
  always_comb if (en) x = d; else t();
  assign y = x;
endmodule
module top(input logic [7:0] d, output logic [7:0] y0, y1);
  child u0(.en(1'b0), .d(d), .y(y0));
  child u1(.en(1'b1), .d(d), .y(y1));
endmodule
