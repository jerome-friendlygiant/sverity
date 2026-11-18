// expect: LNT-080 WARNING 4
interface bus; logic a; endinterface
module top(input logic clk, output logic q);
  virtual bus handle;
  always @(posedge clk) q <= 0;
endmodule
