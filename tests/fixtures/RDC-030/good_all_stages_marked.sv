// sverity-args: --top top --profile fpga
// Near miss of mut_partial_attribute: every stage is marked, so even the fpga profile is satisfied.
// expect-none: RDC-030
module top(input logic clk, arst_n, d, output logic q);
  (* ASYNC_REG = "TRUE" *) logic s1;
  (* ASYNC_REG = "TRUE" *) logic s2;
  always_ff @(posedge clk or negedge arst_n) if (!arst_n) begin s1 <= 1'b0; s2 <= 1'b0; end else begin s1 <= 1'b1; s2 <= s1; end
  always_ff @(posedge clk or negedge s2) if (!s2) q <= 1'b0; else q <= d;
endmodule
