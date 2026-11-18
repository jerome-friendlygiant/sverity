// expect-none: LNT-063
module top(input logic clk, d, output logic q);
  task update; q = d; endtask
  initial q = 0; always @(posedge clk) update();
endmodule
