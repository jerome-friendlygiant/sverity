// expect: LNT-015 WARNING 3
module top(input logic clk, d, output logic q);
  task update; q = d; endtask
  always_ff @(posedge clk) update();
endmodule
