// sverity-args: --top top --sdc @DIR@/release_safe.sdc
// The counter is held (D equals its reset value) by an enable synchronised through a reset chain at release.
// expect-none: RDC-003 WARNING
// expect-none: RDC-002
module top(input logic clk, input logic por_ni, input logic d_i, input logic en_i, output logic [7:0] q_o);
  logic [1:0] rs_q;
  always_ff @(posedge clk or negedge por_ni) if (!por_ni) rs_q <= 2'b00; else rs_q <= {rs_q[0], 1'b1};
  always_ff @(posedge clk or negedge por_ni) if (!por_ni) q_o <= '0; else if (en_i && rs_q[1]) q_o <= q_o + 8'd1;
endmodule
