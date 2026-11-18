// sverity-args: --top top
// expect-confidence: LNT-060 established
module top(input logic clk, output logic y);
  logic [1:0] q;
  task automatic put(output logic v); v = 1; endtask
  always @(posedge clk) put(q[0]);
  assign y = q[0];
endmodule
