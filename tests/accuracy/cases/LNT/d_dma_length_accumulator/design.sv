// Dma length accumulator.
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

    wire [7:0] first_count;
    wire [7:0] second_count;
    assign first_count = {1'b1,a[6:0]};
    assign second_count = {1'b1,b[6:0]};
    assign y = first_count + second_count;

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
