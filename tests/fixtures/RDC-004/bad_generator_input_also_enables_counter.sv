// sverity-args: --top top --sdc @DIR@/generator.sdc
// A counter in the power-on domain also samples the debug reset synchroniser output as an enable: not a reset generator.
// expect: RDC-004 WARNING
// expect: RDC-011 WARNING
module top (input logic clk, input logic por_ni, input logic dbg_rst_ni, output logic [7:0] cnt_o);
  logic [1:0] por_sync_q, dbg_sync_q;
  always_ff @(posedge clk or negedge por_ni)
    if (!por_ni) por_sync_q <= 2'b00; else por_sync_q <= {por_sync_q[0], 1'b1};
  always_ff @(posedge clk or negedge dbg_rst_ni)
    if (!dbg_rst_ni) dbg_sync_q <= 2'b00; else dbg_sync_q <= {dbg_sync_q[0], 1'b1};
  always_ff @(posedge clk or negedge por_sync_q[1])
    if (!por_sync_q[1]) cnt_o <= '0; else if (dbg_sync_q[1]) cnt_o <= cnt_o + 8'd1;
endmodule
