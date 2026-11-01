// sverity-args: --top top
// a blocking write through a concatenation target is read by another clocked process
// expect: LNT-060 WARNING
module top(input logic clk, input logic [1:0] d, output logic y);
  logic a, b;
  always @(posedge clk) {a, b} = d;
  always @(posedge clk) y <= a;
endmodule
