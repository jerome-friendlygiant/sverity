// Stream qualifier bank.
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

    generate
        if (1) begin : monitor
            wire qualifier;
            assign qualifier = valid;
            assign qualifer = valid;
        end
    endgenerate
    assign y = a | b;

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
