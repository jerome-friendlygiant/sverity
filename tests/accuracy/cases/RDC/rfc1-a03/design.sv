// Two-clock status block whose domains each release reset through their own synchroniser.
module top (
    input  logic clk_a,
    input  logic clk_b,
    input  logic por_n,
    input  logic beat_a,
    input  logic beat_b,
    output logic q_a,
    output logic q_b
);
    logic [1:0] rst_a;
    logic [1:0] rst_b;

    always_ff @(posedge clk_a or negedge por_n) begin
        if (!por_n) rst_a <= 2'b00;
        else        rst_a <= {rst_a[0], 1'b1};
    end

    always_ff @(posedge clk_b or negedge por_n) begin
        if (!por_n) rst_b <= 2'b00;
        else        rst_b <= {rst_b[0], 1'b1};
    end

    always_ff @(posedge clk_a or negedge rst_a[1]) begin
        if (!rst_a[1]) q_a <= 1'b0;
        else           q_a <= beat_a;
    end

    always_ff @(posedge clk_b or negedge rst_b[1]) begin
        if (!rst_b[1]) q_b <= 1'b0;
        else           q_b <= beat_b;
    end
endmodule
