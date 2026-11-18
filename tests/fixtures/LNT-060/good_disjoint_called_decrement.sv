// sverity-args: --top top
// expect-none: LNT-060
module top(input logic clk, output logic y);
  logic [1:0] q;
  task automatic dec; q[0]--; endtask
  always @(posedge clk) dec();
  assign y = q[1];
endmodule
