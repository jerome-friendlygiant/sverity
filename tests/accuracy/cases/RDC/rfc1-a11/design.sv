// Reset release block whose clock passes through a gating cell supplied outside the design.
// Library cell: body not supplied.
(* black_box *) module cg_cell (input logic clk_i, input logic en_i, output logic clk_o);
endmodule

module top (
    input  logic       clk,
    input  logic       en,
    input  logic       rst_n,
    input  logic [7:0] d,
    output logic [7:0] q
);
    logic       gclk;
    logic [1:0] rst_sync;

    cg_cell u_cg (
        .clk_i (clk),
        .en_i  (en),
        .clk_o (gclk)
    );

    always_ff @(posedge gclk or negedge rst_n) begin
        if (!rst_n) rst_sync <= 2'b00;
        else        rst_sync <= {rst_sync[0], 1'b1};
    end

    always_ff @(posedge gclk or negedge rst_sync[1]) begin
        if (!rst_sync[1]) q <= '0;
        else              q <= d;
    end
endmodule
