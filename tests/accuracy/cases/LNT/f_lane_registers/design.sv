`default_nettype none
module top (
    input wire clk,
    input wire aux_clk,
    input wire rst_n,
    input wire en,
    input wire [7:0] a,
    input wire [7:0] b,
    input wire [1:0] addr,
    input wire [1:0] addr_b,
    output logic [7:0] y
);

// Byte-oriented lane registers datapath.

parameter integer LANES = 4;
logic [7:0] lane_data [0:LANES-1];
generate
    for (genvar lane = 0; lane < LANES; lane = lane + 1) begin : ports
        always @(posedge clk) begin
            if (en && addr == lane[1:0])
                lane_data[lane] <= a;
        end
    end
endgenerate
assign y = lane_data[addr_b];

endmodule
`default_nettype wire
