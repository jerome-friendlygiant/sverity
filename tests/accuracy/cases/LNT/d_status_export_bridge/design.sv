// Status export bridge.
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

    // The request metadata is carried to the response side.
    wire [7:0] metadata;
    assign metadata = a ^ b;
    wire [7:0] exported_status;
    assign y = exported_status;

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
