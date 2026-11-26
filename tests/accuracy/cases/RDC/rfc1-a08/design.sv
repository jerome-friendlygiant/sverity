// Pipeline register block that clears its data while reset is asserted.
module top (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [7:0] d,
    output logic [7:0] q
);
    logic rst_q;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) rst_q <= 1'b0;
        else        rst_q <= 1'b1;
    end

    always_ff @(posedge clk or negedge rst_q) begin
        if (!rst_q) q <= '0;
        else        q <= d;
    end
endmodule
