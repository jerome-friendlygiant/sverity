// sverity-args: --top top --sdc @DIR@/reset_and_data_mux.sdc
// The reset is ANDed with a value chosen by a register mux between two data registers: the mux does not choose between resets, so the exported reset stays Unresolved.
// expect: RDC-009 WARNING
// expect-unresolved: RDC-009
// expect-coverage: reset-extraction INCOMPLETE
module top (input logic clk_core, input logic clk_link, input logic por_ni, input logic a_i, b_i, m_i, output logic periph_rst_no, output logic core_active_o);
  logic [1:0] core_rst_sync_q; logic a_q, b_q, m_q, g;
  always_ff @(posedge clk_core or negedge por_ni) if (!por_ni) core_rst_sync_q <= 2'b00; else core_rst_sync_q <= {core_rst_sync_q[0], 1'b1};
  always_ff @(posedge clk_link or negedge por_ni) if (!por_ni) begin a_q <= 1'b0; b_q <= 1'b0; m_q <= 1'b0; end else begin a_q <= a_i; b_q <= b_i; m_q <= m_i; end
  assign g = m_q ? a_q : b_q;
  assign periph_rst_no = core_rst_sync_q[1] & g;
  always_ff @(posedge clk_core or negedge por_ni) if (!por_ni) core_active_o <= 1'b0; else core_active_o <= periph_rst_no;
endmodule
