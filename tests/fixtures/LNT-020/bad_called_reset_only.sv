// expect: LNT-020 WARNING 4
module top(input logic clk, rst, output logic q);
  task clear_q; q <= 0; endtask
  always_ff @(posedge clk) if (rst) clear_q();
endmodule
