// Word alignment adapter.
// Interface values are sampled by the downstream transaction stage.
// Combinational paths preserve the transaction qualifier.

module top (
    input wire [7:0] a,

    input wire [7:0] b,

    input wire valid,

    output wire [9:0] y,

    output wire ready

);

    // Datapath and transaction response.

    assign y = {2'b00,a} << 2;
    wire [7:0] tag;
    assign tag = b;

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
