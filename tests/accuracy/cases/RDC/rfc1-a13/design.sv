// Debug trace block whose counter is reset from a system-clock reset flop.
module top (
    input  logic       clk_sys,
    input  logic       clk_dbg,
    input  logic       por_n,
    input  logic       ev,
    output logic [7:0] cnt
);
    logic       sys_rst_q;
    logic [1:0] dbg_rst;

    always_ff @(posedge clk_sys or negedge por_n) begin
        if (!por_n) sys_rst_q <= 1'b0;
        else        sys_rst_q <= 1'b1;
    end

    always_ff @(posedge clk_dbg or negedge sys_rst_q) begin
        if (!sys_rst_q) dbg_rst <= 2'b00;
        else            dbg_rst <= {dbg_rst[0], 1'b1};
    end

    always_ff @(posedge clk_dbg or negedge dbg_rst[1]) begin
        if (!dbg_rst[1]) cnt <= '0;
        else if (ev)     cnt <= cnt + 1'b1;
    end
endmodule
