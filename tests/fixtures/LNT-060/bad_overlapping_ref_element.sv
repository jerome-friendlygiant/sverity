// sverity-args: --top top
// expect-confidence: LNT-060 established
module top(input logic clk, output logic y);
  logic [1:0] q [2];
  task automatic inner(ref logic [1:0] v); y = v[0]; endtask
  task automatic outer(ref logic [1:0] v); inner(v); endtask
  always @(posedge clk) q[0][0]++;
  always @* outer(q[0]);
endmodule
