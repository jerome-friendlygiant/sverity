// Heartbeat block that reports watchdog state on a separate LED clock.
module top (
    input  logic clk_a,
    input  logic clk_b,
    input  logic por_n,
    input  logic beat,
    output logic q_a,
    output logic wdt_led
);
    logic wdt_rst_n;
    logic core_rst_n;
    logic beat_b;

    always_ff @(posedge clk_b or negedge por_n) begin
        if (!por_n) wdt_rst_n <= 1'b0;
        else        wdt_rst_n <= 1'b1;
    end

    assign core_rst_n = por_n & wdt_rst_n;

    always_ff @(posedge clk_a or negedge core_rst_n) begin
        if (!core_rst_n) q_a <= 1'b0;
        else             q_a <= beat;
    end

    always_ff @(posedge clk_b or negedge por_n) begin
        if (!por_n) beat_b <= 1'b0;
        else        beat_b <= beat;
    end

    assign wdt_led = beat_b;
endmodule
