// Credit field formatter.
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

    byte_route #(.W(8)) u_route (.a(a), .b(b), .y(y));

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule

module byte_route #(parameter W=4)(input wire [W-1:0] a,b, output wire [W-1:0] y);
    assign y = a ^ b;
endmodule
