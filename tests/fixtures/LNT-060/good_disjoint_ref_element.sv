// sverity-args: --top top
// expect-none: LNT-060
module top(input logic clk, output logic y);
  logic [1:0] q [2];
  task automatic get(ref logic [1:0] v); y = v[1]; endtask
  always @(posedge clk) q[0][0]++;
  always @* get(q[0]);
endmodule
