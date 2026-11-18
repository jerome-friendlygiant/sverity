// Packet checksum stage.
// Interface values are sampled by the downstream transaction stage.
// Combinational paths preserve the transaction qualifier.

module top (
    input wire [7:0] a,

    input wire [7:0] b,

    input wire valid,

    output wire [7:0] y,

    output wire ready

);

    // Datapath and transaction response.

    packet_fold_core u_fold (.a(a), .b(b), .y(y));

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
