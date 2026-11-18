// Timer reload bridge.
// Interface values are sampled by the downstream transaction stage.
// Combinational paths preserve the transaction qualifier.

module top (
    input wire [11:0] a,

    input wire [7:0] b,

    input wire valid,

    output wire [7:0] y,

    output wire ready

);

    // Datapath and transaction response.

    wire [11:0] reload_value;
    assign reload_value = {4'h1,a[7:0]};
    reload_lane u_lane (.reload(reload_value), .mask(b), .value(y));

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule

module reload_lane(input wire [7:0] reload, mask, output wire [7:0] value);
    assign value = reload | mask;
endmodule
