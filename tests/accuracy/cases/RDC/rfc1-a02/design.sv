// Configuration register block that restores its default on power-on reset.
module top (
    input  logic       clk,
    input  logic       por_n,
    input  logic       sw_rst_n,
    input  logic       we,
    input  logic [7:0] wdata,
    output logic [7:0] cfg,
    output logic [7:0] cfg_shadow
);
    always_ff @(posedge clk or negedge por_n) begin
        if (!por_n) cfg <= 8'h5a;
        else if (we) cfg <= wdata;
    end

    always_ff @(posedge clk or negedge por_n) begin
        if (!por_n) cfg_shadow <= 8'h5a;
        else        cfg_shadow <= cfg;
    end
endmodule
