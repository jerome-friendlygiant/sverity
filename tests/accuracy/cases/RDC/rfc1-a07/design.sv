// Reset release synchroniser for a register clocked by clk_b.
module top (
    input  logic clk_a,
    input  logic clk_b,
    input  logic por_n,
    input  logic d,
    output logic q
);
    logic [1:0] rst_sync;

    always_ff @(posedge clk_a or negedge por_n) begin
        if (!por_n) rst_sync[0] <= 1'b0;
        else        rst_sync[0] <= 1'b1;
    end

    always_ff @(posedge clk_b or negedge por_n) begin
        if (!por_n) rst_sync[1] <= 1'b0;
        else        rst_sync[1] <= rst_sync[0];
    end

    always_ff @(posedge clk_b or negedge rst_sync[1]) begin
        if (!rst_sync[1]) q <= 1'b0;
        else              q <= d;
    end
endmodule
