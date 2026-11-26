// Pair of clock-B registers that share a reset with a clock-A block.
module top (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_raw_n,
    input  logic d_a,
    output logic q_a,
    output logic q_b
);
    always_ff @(posedge clk_a or negedge rst_raw_n) begin
        if (!rst_raw_n) q_a <= 1'b0;
        else            q_a <= d_a;
    end

    always_ff @(posedge clk_b or negedge rst_raw_n) begin
        if (!rst_raw_n) q_b <= 1'b0;
        else            q_b <= q_a;
    end
endmodule
