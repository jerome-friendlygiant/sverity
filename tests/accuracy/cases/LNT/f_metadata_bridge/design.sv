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

// Byte-oriented metadata bridge datapath.

typedef struct packed {
    logic [3:0] tag;
    logic [3:0] count;
} record_t;
record_t records [0:3];
always @(posedge clk) begin
    records[2'd1] <= {a[7:4], a[3:0]};
end
always @(posedge aux_clk) begin
    y <= records[2'd1];
end

endmodule
`default_nettype wire
