// Output selector that chooses between functional data and a scan-test pattern.
module top (
    input  logic       clk,
    input  logic       rst_n,
    input  logic       scan_en,
    input  logic [7:0] func_d,
    input  logic [7:0] scan_d,
    output logic [7:0] q
);
    logic [1:0] rst_sync;
    logic [7:0] sel_d;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) rst_sync <= 2'b00;
        else        rst_sync <= {rst_sync[0], 1'b1};
    end

    assign sel_d = scan_en ? scan_d : func_d;

    always_ff @(posedge clk or negedge rst_sync[1]) begin
        if (!rst_sync[1]) q <= '0;
        else              q <= sel_d;
    end
endmodule
