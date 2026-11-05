// sverity-args: --top top
// One stage of a reset synchroniser carries the attribute and one does not.
// revert: logic s2; => (* ASYNC_REG = "TRUE" *) logic s2;
// revert-expect-none: RDC-030
// expect: RDC-030 WARNING
module top(input logic clk, arst_n, d, output logic q);
  (* ASYNC_REG = "TRUE" *) logic s1;
  logic s2;
  always_ff @(posedge clk or negedge arst_n) if (!arst_n) begin s1 <= 1'b0; s2 <= 1'b0; end else begin s1 <= 1'b1; s2 <= s1; end
  always_ff @(posedge clk or negedge s2) if (!s2) q <= 1'b0; else q <= d;
endmodule
