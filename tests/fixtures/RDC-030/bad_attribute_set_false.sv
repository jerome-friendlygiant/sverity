// sverity-args: --top top --profile fpga
// ASYNC_REG = "FALSE" asks for the opposite of a synchroniser's placement, so it does not count as marked.
// expect: RDC-030 WARNING
module top(input logic clk, arst_n, d, output logic q);
  (* ASYNC_REG = "FALSE" *) logic s1;
  (* ASYNC_REG = "FALSE" *) logic s2;
  always_ff @(posedge clk or negedge arst_n) if (!arst_n) begin s1 <= 1'b0; s2 <= 1'b0; end else begin s1 <= 1'b1; s2 <= s1; end
  always_ff @(posedge clk or negedge s2) if (!s2) q <= 1'b0; else q <= d;
endmodule
