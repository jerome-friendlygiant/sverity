// Burst sum formatter.
// Interface values are sampled by the downstream transaction stage.
// Combinational paths preserve the transaction qualifier.

module top (
    input wire [7:0] a,

    input wire [7:0] b,

    input wire valid,

    output wire [8:0] y,

    output wire ready

);

    // Datapath and transaction response.

    assign y = {1'b0,a} + {1'b0,b};

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
