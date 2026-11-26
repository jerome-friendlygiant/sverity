// sverity-args: --top top --sdc @DIR@/release_flop.sdc
// revert: always_ff @(posedge clk_dbg or negedge sys_rst_q) => always_ff @(posedge clk_sys or negedge sys_rst_q)
// revert-expect-none: RDC-007 ERROR
// The clk_sys release flop drives an ordinary clk_dbg counter reset directly: not a release flop.
// expect: RDC-001 WARNING
// expect: RDC-007 ERROR
module top (input logic clk_sys, input logic clk_dbg, input logic por_n, input logic ev, output logic [7:0] cnt);
    logic sys_rst_q;
    always_ff @(posedge clk_sys or negedge por_n) if (!por_n) sys_rst_q <= 1'b0; else sys_rst_q <= 1'b1;
    always_ff @(posedge clk_dbg or negedge sys_rst_q) if (!sys_rst_q) cnt <= '0; else if (ev) cnt <= cnt + 1'b1;
endmodule
