// Address lane expander.
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

    wire [7:0] region_offset;
    assign region_offset = {1'b1,a[6:0]};
    assign y = region_offset << 2;
    wire [7:0] tag;
    assign tag = b;

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
