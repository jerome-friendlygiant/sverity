// Sensor lane adapter.
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

    wire [7:0] monitor;
    assign monitor = a;
    byte_select u_lane (.a(a), .b(b), .y(y), .result(monitor));

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule

module byte_select(input wire [7:0] a, input wire [7:0] b, output wire [7:0] y);
    assign y = a ^ b;
endmodule
