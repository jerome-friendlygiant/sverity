// Event counter whose reset is produced by a power-on generator block.
// Library cell: body not supplied.
(* black_box *) module por_gen (input logic clk, input logic pin_i, output logic rst_no);
endmodule

module top (
    input  logic       clk,
    input  logic       ext_pin,
    input  logic       en,
    output logic [3:0] count
);
    logic core_rst_n;

    por_gen u_por (
        .clk    (clk),
        .pin_i  (ext_pin),
        .rst_no (core_rst_n)
    );

    always_ff @(posedge clk or negedge core_rst_n) begin
        if (!core_rst_n) count <= '0;
        else if (en)     count <= count + 1'b1;
    end
endmodule
