// Status register whose reset is released through a two-stage synchroniser on the system clock.
module top (
    input  logic clk,
    input  logic ext_rst_n,
    input  logic d,
    output logic q
);
    logic [1:0] rst_sync;

    always_ff @(posedge clk or negedge ext_rst_n) begin
        if (!ext_rst_n) rst_sync <= 2'b00;
        else            rst_sync <= {rst_sync[0], 1'b1};
    end

    always_ff @(posedge clk or negedge rst_sync[1]) begin
        if (!rst_sync[1]) q <= 1'b0;
        else              q <= d;
    end
endmodule
