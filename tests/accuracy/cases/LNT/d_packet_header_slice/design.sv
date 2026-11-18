// Packet header slice.
// Interface values are sampled by the downstream transaction stage.
// Combinational paths preserve the transaction qualifier.

module top (
    input wire [7:0] a,

    input wire [7:0] b,

    input wire valid,

    output wire [3:0] y,

    output wire ready

);

    // Datapath and transaction response.

    assign y = a[9:6];
    wire [7:0] tag;
    assign tag = b;

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
