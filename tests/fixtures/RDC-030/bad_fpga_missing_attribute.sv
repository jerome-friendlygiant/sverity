// sverity-args: --top top --profile fpga
// Under profile fpga, a reset synchroniser needs its stages marked.
// expect: RDC-030 WARNING
module top(input logic clk, arst_n, d, output logic q);
  logic s1;
  logic s2;
  always_ff @(posedge clk or negedge arst_n) if (!arst_n) begin s1 <= 1'b0; s2 <= 1'b0; end else begin s1 <= 1'b1; s2 <= s1; end
  always_ff @(posedge clk or negedge s2) if (!s2) q <= 1'b0; else q <= d;
endmodule
