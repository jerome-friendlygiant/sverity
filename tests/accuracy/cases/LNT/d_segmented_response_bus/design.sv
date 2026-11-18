// Segmented response bus.
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

    assign y[7:4] = a[7:4];
    assign y[3:0] = b[3:0];

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
